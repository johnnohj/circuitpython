/**
 * StorageController - Manages filesystem storage
 *
 * This bridges CircuitPython's filesystem (storage module) with persistent storage.
 * It supports both browser (IndexedDB) and Node.js (in-memory or filesystem) environments.
 *
 * Handles:
 * - Virtual filesystem backed by IndexedDB (browser) or in-memory store (Node.js)
 * - storage.getmount(), storage.remount(), storage.erase_filesystem()
 * - File operations for Python's open(), os.listdir(), etc.
 * - Integration with C-side filesystem code
 */

export class StorageController {
  /**
   * Create a new storage controller
   * @param {Object} wasmModule - The loaded WASM module
   * @param {CircuitPythonBoard} board - The parent board instance
   */
  constructor(wasmModule, board) {
    this.module = wasmModule
    this.board = board

    // Storage configuration
    this.storagePrefix = board.config.storagePrefix || 'circuitpy'
    this.dbName = `${this.storagePrefix}-filesystem`
    this.storeName = 'files'

    // Storage backend (IndexedDB or in-memory)
    this.backend = null
    this.backendType = null

    // Mount state
    this.mounted = false
    this.readonly = false

    // File cache (in-memory cache of file contents)
    this.fileCache = new Map()

    // Detect environment
    this.isNode = typeof process !== 'undefined' && process.versions && process.versions.node
    this.isBrowser = typeof window !== 'undefined' && typeof window.indexedDB !== 'undefined'
  }

  /**
   * Initialize storage system
   * Opens appropriate storage backend and mounts the virtual filesystem
   */
  async initialize() {
    console.log('[Storage] Initializing...')

    try {
      // Initialize storage backend based on environment
      if (this.isBrowser) {
        console.log('[Storage] Using IndexedDB backend (browser)')
        this.backend = await this._openIndexedDB()
        this.backendType = 'indexeddb'
      } else if (this.isNode) {
        console.log('[Storage] Using in-memory backend (Node.js)')
        this.backend = this._createInMemoryBackend()
        this.backendType = 'memory'
      } else {
        throw new Error('Unable to determine storage backend')
      }

      // Mount the filesystem
      await this._mountFilesystem()

      this.mounted = true
      console.log('[Storage] Initialized successfully')
    } catch (error) {
      console.error('[Storage] Initialization failed:', error)
      throw error
    }
  }

  /**
   * Open IndexedDB database (browser)
   * @private
   */
  async _openIndexedDB() {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(this.dbName, 1)

      request.onerror = () => {
        reject(new Error(`Failed to open IndexedDB: ${request.error}`))
      }

      request.onsuccess = () => {
        console.log('[Storage] IndexedDB opened')
        resolve(request.result)
      }

      request.onupgradeneeded = (event) => {
        console.log('[Storage] Creating IndexedDB schema...')
        const db = event.target.result

        // Create object store for files
        if (!db.objectStoreNames.contains(this.storeName)) {
          db.createObjectStore(this.storeName, { keyPath: 'path' })
        }
      }
    })
  }

  /**
   * Create in-memory storage backend (Node.js fallback)
   * @private
   */
  _createInMemoryBackend() {
    // Simple Map-based in-memory storage
    const storage = new Map()

    return {
      type: 'memory',
      storage,

      async get(path) {
        return storage.get(path) || null
      },

      async put(file) {
        storage.set(file.path, file)
      },

      async delete(path) {
        storage.delete(path)
      },

      async getAll() {
        return Array.from(storage.values())
      },

      async clear() {
        storage.clear()
      },

      close() {
        // No-op for memory backend
      }
    }
  }

  /**
   * Mount the filesystem
   * This connects IndexedDB to the WASM filesystem layer
   * @private
   */
  async _mountFilesystem() {
    console.log('[Storage] Mounting filesystem...')

    // Create default files if they don't exist
    await this._ensureDefaultFiles()

    // TODO: Call C-side filesystem initialization if available
    // This would connect to the VFS layer in CircuitPython

    console.log('[Storage] Filesystem mounted')
  }

  /**
   * Ensure default files exist (boot.py, code.py, etc.)
   * @private
   */
  async _ensureDefaultFiles() {
    const defaultFiles = [
      {
        path: '/boot.py',
        content: '# boot.py -- runs on boot-up\n'
      },
      {
        path: '/code.py',
        content: '# code.py -- main program\nimport board\nimport time\n\nprint("Hello from CircuitPython WASM!")\n'
      }
    ]

    for (const file of defaultFiles) {
      const exists = await this.fileExists(file.path)
      if (!exists) {
        await this.writeFile(file.path, file.content)
        console.log(`[Storage] Created default file: ${file.path}`)
      }
    }
  }

  /**
   * Check if a file exists
   * @param {string} path - File path
   * @returns {Promise<boolean>} True if file exists
   */
  async fileExists(path) {
    try {
      const file = await this._getFile(path)
      return file !== null
    } catch (e) {
      return false
    }
  }

  /**
   * Read a file
   * @param {string} path - File path
   * @returns {Promise<string>} File contents
   */
  async readFile(path) {
    const file = await this._getFile(path)
    if (!file) {
      throw new Error(`File not found: ${path}`)
    }
    return file.content
  }

  /**
   * Write a file
   * @param {string} path - File path
   * @param {string} content - File contents
   */
  async writeFile(path, content) {
    if (this.readonly) {
      throw new Error('Filesystem is read-only')
    }

    await this._putFile({
      path,
      content,
      modified: Date.now()
    })

    // Update cache
    this.fileCache.set(path, content)
  }

  /**
   * Delete a file
   * @param {string} path - File path
   */
  async deleteFile(path) {
    if (this.readonly) {
      throw new Error('Filesystem is read-only')
    }

    await this._deleteFile(path)

    // Clear cache
    this.fileCache.delete(path)
  }

  /**
   * List files in a directory
   * @param {string} path - Directory path (default: '/')
   * @returns {Promise<string[]>} Array of filenames
   */
  async listFiles(path = '/') {
    const files = await this._getAllFiles()

    // Filter files in the specified directory
    const dirPrefix = path === '/' ? '' : path
    const items = []

    for (const file of files) {
      if (file.path.startsWith(dirPrefix)) {
        const relativePath = file.path.substring(dirPrefix.length)
        const parts = relativePath.split('/').filter(p => p)

        if (parts.length > 0) {
          const name = parts[0]
          if (!items.includes(name)) {
            items.push(name)
          }
        }
      }
    }

    return items
  }

  /**
   * Get file stats (size, modification time, etc.)
   * @param {string} path - File path
   * @returns {Promise<Object>} File stats
   */
  async getFileStats(path) {
    const file = await this._getFile(path)
    if (!file) {
      throw new Error(`File not found: ${path}`)
    }

    return {
      size: file.content.length,
      modified: file.modified,
      isFile: true,
      isDirectory: false
    }
  }

  /**
   * Erase the entire filesystem
   * This implements storage.erase_filesystem() from Python
   */
  async eraseFilesystem() {
    console.log('[Storage] Erasing filesystem...')

    if (this.readonly) {
      throw new Error('Filesystem is read-only')
    }

    // Clear all files from IndexedDB
    await this._clearAllFiles()

    // Clear cache
    this.fileCache.clear()

    // Recreate default files
    await this._ensureDefaultFiles()

    console.log('[Storage] Filesystem erased')
  }

  /**
   * Remount filesystem with different readonly setting
   * This implements storage.remount() from Python
   *
   * @param {boolean} readonly - Whether filesystem should be readonly
   */
  async remount(readonly) {
    console.log(`[Storage] Remounting as ${readonly ? 'readonly' : 'read-write'}...`)

    this.readonly = readonly

    // TODO: Call C-side remount if available

    console.log('[Storage] Remount complete')
  }

  /**
   * Get mount information
   * This implements storage.getmount() from Python
   *
   * @returns {Object} Mount information
   */
  getMountInfo() {
    return {
      mounted: this.mounted,
      readonly: this.readonly,
      label: this.storagePrefix,
      backend: 'IndexedDB'
    }
  }

  /**
   * Cleanup storage controller
   */
  async cleanup() {
    console.log('[Storage] Cleaning up...')

    if (this.backend) {
      if (this.backendType === 'indexeddb') {
        this.backend.close()
      }
      this.backend = null
    }

    this.mounted = false
    this.fileCache.clear()

    console.log('[Storage] Cleanup complete')
  }

  // ===== IndexedDB Operations =====

  /**
   * Get a file from storage backend
   * @private
   */
  async _getFile(path) {
    if (this.backendType === 'memory') {
      return this.backend.get(path)
    } else {
      // IndexedDB
      return new Promise((resolve, reject) => {
        const transaction = this.backend.transaction([this.storeName], 'readonly')
        const store = transaction.objectStore(this.storeName)
        const request = store.get(path)

        request.onsuccess = () => resolve(request.result || null)
        request.onerror = () => reject(request.error)
      })
    }
  }

  /**
   * Put a file into storage backend
   * @private
   */
  async _putFile(file) {
    if (this.backendType === 'memory') {
      return this.backend.put(file)
    } else {
      // IndexedDB
      return new Promise((resolve, reject) => {
        const transaction = this.backend.transaction([this.storeName], 'readwrite')
        const store = transaction.objectStore(this.storeName)
        const request = store.put(file)

        request.onsuccess = () => resolve()
        request.onerror = () => reject(request.error)
      })
    }
  }

  /**
   * Delete a file from storage backend
   * @private
   */
  async _deleteFile(path) {
    if (this.backendType === 'memory') {
      return this.backend.delete(path)
    } else {
      // IndexedDB
      return new Promise((resolve, reject) => {
        const transaction = this.backend.transaction([this.storeName], 'readwrite')
        const store = transaction.objectStore(this.storeName)
        const request = store.delete(path)

        request.onsuccess = () => resolve()
        request.onerror = () => reject(request.error)
      })
    }
  }

  /**
   * Get all files from storage backend
   * @private
   */
  async _getAllFiles() {
    if (this.backendType === 'memory') {
      return this.backend.getAll()
    } else {
      // IndexedDB
      return new Promise((resolve, reject) => {
        const transaction = this.backend.transaction([this.storeName], 'readonly')
        const store = transaction.objectStore(this.storeName)
        const request = store.getAll()

        request.onsuccess = () => resolve(request.result || [])
        request.onerror = () => reject(request.error)
      })
    }
  }

  /**
   * Clear all files from storage backend
   * @private
   */
  async _clearAllFiles() {
    if (this.backendType === 'memory') {
      return this.backend.clear()
    } else {
      // IndexedDB
      return new Promise((resolve, reject) => {
        const transaction = this.backend.transaction([this.storeName], 'readwrite')
        const store = transaction.objectStore(this.storeName)
        const request = store.clear()

        request.onsuccess = () => resolve()
        request.onerror = () => reject(request.error)
      })
    }
  }
}
