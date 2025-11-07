/**
 * CircuitPython WASM Filesystem
 *
 * Provides persistent storage for CircuitPython files using IndexedDB.
 * Implements the StoragePeripheral interface for use with the os module.
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
        this.cwd = '/home';  // Current working directory - CircuitPython user space
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

        // Resolve relative paths
        const resolvedPath = this._resolvePath(path);

        // Convert content to Uint8Array if it's a string
        const data = typeof content === 'string'
            ? new TextEncoder().encode(content)
            : content;

        // Create parent directories if needed
        await this._ensureParentDirs(resolvedPath);

        const fileRecord = {
            path: resolvedPath,
            content: data,
            modified: Date.now() / 1000,  // Unix timestamp in seconds
            size: data.length,
            isDirectory: false
        };

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.put(fileRecord);

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Wrote ${resolvedPath} (${data.length} bytes)`);
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to write ${resolvedPath}: ${request.error}`));
            };
        });
    }

    /**
     * Read a file from IndexedDB
     * Returns Uint8Array content or null if file doesn't exist
     */
    async readFile(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        // Resolve relative paths
        const resolvedPath = this._resolvePath(path);

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.get(resolvedPath);

            request.onsuccess = () => {
                if (request.result) {
                    if (this.verbose) {
                        console.log(`[CircuitPython FS] Read ${resolvedPath} (${request.result.size} bytes)`);
                    }
                    resolve(request.result.content);
                } else {
                    // Return null for missing files (StoragePeripheral interface)
                    resolve(null);
                }
            };

            request.onerror = () => {
                reject(new Error(`Failed to read ${resolvedPath}: ${request.error}`));
            };
        });
    }

    /**
     * Delete a file from IndexedDB
     * Throws error if file doesn't exist or is a directory
     */
    async deleteFile(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const resolvedPath = this._resolvePath(path);

        // Check if it's a directory
        const fileInfo = await this._getFileInfo(resolvedPath);
        if (!fileInfo) {
            throw new Error(`File not found: ${resolvedPath}`);
        }
        if (fileInfo.isDirectory) {
            throw new Error(`Cannot delete directory with deleteFile: ${resolvedPath}`);
        }

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.delete(resolvedPath);

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Deleted ${resolvedPath}`);
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to delete ${resolvedPath}: ${request.error}`));
            };
        });
    }

    /**
     * Check if a file or directory exists
     */
    async exists(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const resolvedPath = this._resolvePath(path);

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.get(resolvedPath);

            request.onsuccess = () => {
                resolve(request.result !== undefined);
            };

            request.onerror = () => {
                reject(new Error(`Failed to check ${resolvedPath}: ${request.error}`));
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
        let syncedCount = 0;

        for (const fileInfo of files) {
            try {
                // Skip directory entries - they'll be created as needed by files
                if (fileInfo.isDirectory) {
                    if (this.verbose) {
                        console.log(`[CircuitPython FS] Skipping directory: ${fileInfo.path}`);
                    }
                    continue;
                }

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
                syncedCount++;

                if (this.verbose) {
                    console.log(`[CircuitPython FS] Synced to VFS: ${fileInfo.path}`);
                }
            } catch (error) {
                console.error(`[CircuitPython FS] Failed to sync ${fileInfo.path}:`, error);
            }
        }

        if (this.verbose) {
            console.log(`[CircuitPython FS] Synced ${syncedCount} files to VFS`);
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

    // ========== StoragePeripheral Interface Methods ==========

    /**
     * Get current working directory
     */
    getcwd() {
        return this.cwd;
    }

    /**
     * Change current working directory
     */
    chdir(path) {
        const resolvedPath = this._resolvePath(path);
        // Validate that path exists (optional - could be made async to check DB)
        this.cwd = resolvedPath;
        if (this.verbose) {
            console.log(`[CircuitPython FS] Changed directory to ${this.cwd}`);
        }
    }

    /**
     * Create a directory
     * Creates parent directories if needed
     */
    async mkdir(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const resolvedPath = this._resolvePath(path);

        // Check if directory already exists
        const existing = await this._getFileInfo(resolvedPath);
        if (existing) {
            if (existing.isDirectory) {
                // Directory already exists - this is OK
                return;
            } else {
                throw new Error(`File exists at path: ${resolvedPath}`);
            }
        }

        // Create parent directories recursively
        await this._ensureParentDirs(resolvedPath);

        // Create the directory entry
        const dirRecord = {
            path: resolvedPath,
            content: null,
            modified: Date.now() / 1000,
            size: 0,
            isDirectory: true
        };

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.put(dirRecord);

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Created directory ${resolvedPath}`);
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to create directory ${resolvedPath}: ${request.error}`));
            };
        });
    }

    /**
     * Remove an empty directory
     * Throws error if directory is not empty
     */
    async rmdir(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const resolvedPath = this._resolvePath(path);

        // Check if it's a directory
        const fileInfo = await this._getFileInfo(resolvedPath);
        if (!fileInfo) {
            throw new Error(`Directory not found: ${resolvedPath}`);
        }
        if (!fileInfo.isDirectory) {
            throw new Error(`Not a directory: ${resolvedPath}`);
        }

        // Check if directory is empty
        const contents = await this.listdir(resolvedPath);
        if (contents.length > 0) {
            throw new Error(`Directory not empty: ${resolvedPath}`);
        }

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.delete(resolvedPath);

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Removed directory ${resolvedPath}`);
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to remove directory ${resolvedPath}: ${request.error}`));
            };
        });
    }

    /**
     * List files and directories in a directory
     * Returns array of names (not full paths)
     */
    async listdir(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const resolvedPath = this._resolvePath(path);
        const prefix = resolvedPath === '/' ? '/' : resolvedPath + '/';

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.getAll();

            request.onsuccess = () => {
                const names = new Set();

                for (const file of request.result) {
                    if (file.path.startsWith(prefix) && file.path !== resolvedPath) {
                        // Get the relative path after the prefix
                        const relativePath = file.path.substring(prefix.length);
                        // Get just the first component (file or directory name)
                        const name = relativePath.split('/')[0];
                        if (name) {
                            names.add(name);
                        }
                    }
                }

                const result = Array.from(names);
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Listed ${result.length} items in ${resolvedPath}`);
                }
                resolve(result);
            };

            request.onerror = () => {
                reject(new Error(`Failed to list directory ${resolvedPath}: ${request.error}`));
            };
        });
    }

    /**
     * Get file/directory metadata
     * Returns null if path doesn't exist
     */
    async stat(path) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const resolvedPath = this._resolvePath(path);
        const fileInfo = await this._getFileInfo(resolvedPath);

        if (!fileInfo) {
            return null;
        }

        // Return stat object matching StoragePeripheral interface
        return {
            size: fileInfo.size || 0,
            mode: fileInfo.isDirectory ? 0o040777 : 0o100666,
            isDirectory: fileInfo.isDirectory,
            mtime: fileInfo.modified || (Date.now() / 1000),
            atime: fileInfo.modified || (Date.now() / 1000),
            ctime: fileInfo.modified || (Date.now() / 1000)
        };
    }

    /**
     * Get filesystem statistics (optional)
     * Returns null as IndexedDB doesn't provide this info easily
     */
    statvfs(_path) {
        // IndexedDB doesn't easily provide filesystem stats
        // Could be implemented with navigator.storage.estimate() if needed
        return null;
    }

    /**
     * Set file modification time (optional)
     */
    async utime(path, _atime, mtime) {
        if (!this.db) {
            throw new Error('Database not initialized');
        }

        const resolvedPath = this._resolvePath(path);
        const fileInfo = await this._getFileInfo(resolvedPath);

        if (!fileInfo) {
            throw new Error(`File not found: ${resolvedPath}`);
        }

        // Update the modified timestamp
        fileInfo.modified = mtime;

        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.put(fileInfo);

            request.onsuccess = () => {
                if (this.verbose) {
                    console.log(`[CircuitPython FS] Updated mtime for ${resolvedPath}`);
                }
                resolve();
            };

            request.onerror = () => {
                reject(new Error(`Failed to update time for ${resolvedPath}: ${request.error}`));
            };
        });
    }

    // ========== Helper Methods ==========

    /**
     * Resolve a path relative to current working directory
     */
    _resolvePath(path) {
        if (path.startsWith('/')) {
            // Absolute path
            return path;
        }

        // Relative path - resolve against cwd
        if (this.cwd === '/') {
            return '/' + path;
        }
        return this.cwd + '/' + path;
    }

    /**
     * Get file/directory info from database
     */
    async _getFileInfo(path) {
        return new Promise((resolve, reject) => {
            const transaction = this.db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.get(path);

            request.onsuccess = () => {
                resolve(request.result || null);
            };

            request.onerror = () => {
                reject(new Error(`Failed to get file info for ${path}: ${request.error}`));
            };
        });
    }

    /**
     * Ensure parent directories exist
     */
    async _ensureParentDirs(path) {
        const parts = path.split('/').filter(p => p);
        if (parts.length <= 1) {
            return; // No parent directory or root
        }

        let currentPath = '';
        for (let i = 0; i < parts.length - 1; i++) {
            currentPath += '/' + parts[i];

            const existing = await this._getFileInfo(currentPath);
            if (!existing) {
                // Create directory entry
                const dirRecord = {
                    path: currentPath,
                    content: null,
                    modified: Date.now() / 1000,
                    size: 0,
                    isDirectory: true
                };

                await new Promise((resolve, reject) => {
                    const transaction = this.db.transaction([STORE_NAME], 'readwrite');
                    const store = transaction.objectStore(STORE_NAME);
                    const request = store.put(dirRecord);
                    request.onsuccess = () => resolve();
                    request.onerror = () => reject(new Error(`Failed to create directory ${currentPath}`));
                });
            }
        }
    }

    // ========== Utility Methods ==========

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
