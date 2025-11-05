/**
 * WorkflowController - Manages development workflow and web app connections
 *
 * This is the JS-side implementation of supervisor/workflow.h
 * It manages the "workflow" - the connection between CircuitPython and development tools.
 *
 * In traditional CircuitPython:
 * - USB workflow: Serial connection over USB
 * - Web workflow: HTTP/WebSocket server for web IDE
 * - BLE workflow: Bluetooth connection
 *
 * In WASM CircuitPython:
 * - Web workflow: Direct JS API connection (CircuitPythonBoard instance)
 *
 * The workflow system handles:
 * - Connection management (web apps connecting to board)
 * - Active development state (is user currently coding?)
 * - File upload/download
 * - Board status monitoring
 * - REPL access coordination
 */

export class WorkflowController {
  /**
   * Create a new workflow controller
   * @param {CircuitPythonBoard} board - The parent board instance
   */
  constructor(board) {
    this.board = board

    // Workflow state
    this.active = false
    this.started = false

    // Connected clients (web apps, IDEs, terminals, etc.)
    this.connections = new Set()

    // File operation tracking
    this.pendingFileOps = new Map()

    // Activity tracking
    this.lastActivity = Date.now()
    this.activityTimeout = 60000  // Consider inactive after 60s of no activity
  }

  /**
   * Start the workflow
   * This is called when the board connects and becomes available for development
   */
  async start() {
    if (this.started) {
      console.log('[Workflow] Already started')
      return
    }

    console.log('[Workflow] Starting workflow...')

    // Mark as started
    this.started = true
    this.active = true
    this.lastActivity = Date.now()

    // Advertise service (make board discoverable)
    this._advertiseService()

    // Set up activity monitoring
    this._startActivityMonitoring()

    console.log('[Workflow] Workflow started and active')
  }

  /**
   * Stop the workflow
   */
  async stop() {
    if (!this.started) {
      return
    }

    console.log('[Workflow] Stopping workflow...')

    // Disconnect all connections
    for (const connection of this.connections) {
      await this.unregisterConnection(connection)
    }

    this.started = false
    this.active = false

    console.log('[Workflow] Workflow stopped')
  }

  /**
   * Reset workflow state
   * Called during soft reset
   */
  reset() {
    console.log('[Workflow] Resetting workflow...')

    // Clear pending operations
    this.pendingFileOps.clear()

    // Update activity
    this.lastActivity = Date.now()

    // Keep connections alive through reset (they persist)
    // This matches CircuitPython behavior - USB/web connections survive soft reset

    console.log('[Workflow] Workflow reset complete')
  }

  /**
   * Check if workflow is active
   * Returns true when user is actively developing
   *
   * Active means:
   * - Board is running
   * - REPL is active OR code is executing
   * - A web app/IDE is connected
   * - Recent activity detected
   *
   * @returns {boolean} True if actively developing
   */
  isActive() {
    if (!this.started) {
      return false
    }

    // Check if board is running
    if (!this.board.supervisor || !this.board.supervisor.state.running) {
      return false
    }

    // Active if REPL is running
    if (this.board.supervisor.state.inREPL) {
      return true
    }

    // Active if connections are present
    if (this.connections.size > 0) {
      return true
    }

    // Active if recent activity
    const timeSinceActivity = Date.now() - this.lastActivity
    if (timeSinceActivity < this.activityTimeout) {
      return true
    }

    return false
  }

  /**
   * Request background processing
   * This hints to the supervisor that workflow needs CPU time
   */
  requestBackground() {
    if (!this.started) {
      return
    }

    // Mark activity
    this._markActivity()

    // Trigger supervisor background tasks
    if (this.board.supervisor) {
      // This will be picked up by the supervisor's background loop
      this.active = true
    }
  }

  /**
   * Register a connection (web app, IDE, terminal, etc.)
   *
   * @param {Object} connection - Connection object
   * @param {string} connection.id - Unique connection ID
   * @param {string} connection.type - Connection type (ide, terminal, monitor, etc.)
   * @param {Object} connection.metadata - Optional metadata
   * @returns {string} Connection ID
   */
  registerConnection(connection) {
    if (!connection.id) {
      connection.id = this._generateConnectionId()
    }

    if (this.connections.has(connection)) {
      console.warn(`[Workflow] Connection ${connection.id} already registered`)
      return connection.id
    }

    this.connections.add(connection)
    this._markActivity()

    console.log(`[Workflow] Registered connection: ${connection.id} (${connection.type || 'unknown'})`)

    // Notify board of new connection
    this._notifyConnectionChange()

    return connection.id
  }

  /**
   * Unregister a connection
   *
   * @param {Object|string} connectionOrId - Connection object or ID
   */
  async unregisterConnection(connectionOrId) {
    let connection = connectionOrId

    // If string ID provided, find connection
    if (typeof connectionOrId === 'string') {
      connection = this._findConnection(connectionOrId)
      if (!connection) {
        console.warn(`[Workflow] Connection ${connectionOrId} not found`)
        return
      }
    }

    if (!this.connections.has(connection)) {
      return
    }

    this.connections.delete(connection)

    console.log(`[Workflow] Unregistered connection: ${connection.id}`)

    // Notify board of connection change
    this._notifyConnectionChange()

    // If no more connections, might become inactive
    if (this.connections.size === 0) {
      this._updateActiveState()
    }
  }

  /**
   * Get all active connections
   * @returns {Array} Array of connection objects
   */
  getConnections() {
    return Array.from(this.connections)
  }

  /**
   * Get connection count
   * @returns {number} Number of active connections
   */
  getConnectionCount() {
    return this.connections.size
  }

  /**
   * Upload file to board storage
   *
   * @param {string} path - File path (e.g., '/code.py')
   * @param {string|Uint8Array} content - File content
   * @returns {Promise<void>}
   */
  async uploadFile(path, content) {
    console.log(`[Workflow] Uploading file: ${path}`)

    this._markActivity()

    // Write to storage
    const storage = this.board.storage
    if (!storage) {
      throw new Error('Storage not available')
    }

    await storage.writeFile(path, content)

    console.log(`[Workflow] File uploaded: ${path}`)

    // If uploading code.py, might want to trigger autoreload
    if (path === '/code.py' && this.board.supervisor.state.autoreload) {
      // TODO: Implement autoreload trigger
      console.log('[Workflow] code.py uploaded, autoreload pending...')
    }
  }

  /**
   * Download file from board storage
   *
   * @param {string} path - File path
   * @returns {Promise<string>} File content
   */
  async downloadFile(path) {
    console.log(`[Workflow] Downloading file: ${path}`)

    this._markActivity()

    const storage = this.board.storage
    if (!storage) {
      throw new Error('Storage not available')
    }

    const content = await storage.readFile(path)

    console.log(`[Workflow] File downloaded: ${path}`)

    return content
  }

  /**
   * List files in a directory
   *
   * @param {string} path - Directory path (default: '/')
   * @returns {Promise<string[]>} Array of filenames
   */
  async listFiles(path = '/') {
    this._markActivity()

    const storage = this.board.storage
    if (!storage) {
      throw new Error('Storage not available')
    }

    return storage.listFiles(path)
  }

  /**
   * Delete a file
   *
   * @param {string} path - File path
   * @returns {Promise<void>}
   */
  async deleteFile(path) {
    console.log(`[Workflow] Deleting file: ${path}`)

    this._markActivity()

    const storage = this.board.storage
    if (!storage) {
      throw new Error('Storage not available')
    }

    await storage.deleteFile(path)

    console.log(`[Workflow] File deleted: ${path}`)
  }

  /**
   * Get board status
   *
   * @returns {Object} Board status information
   */
  getStatus() {
    return {
      connected: this.board.isConnected(),
      workflowActive: this.isActive(),
      workflowStarted: this.started,
      connections: this.connections.size,
      inREPL: this.board.supervisor?.state.inREPL || false,
      running: this.board.supervisor?.state.running || false,
      safeMode: this.board.supervisor?.state.safeMode || false,
      autoreload: this.board.supervisor?.state.autoreload || false,
      lastActivity: this.lastActivity,
      timeSinceActivity: Date.now() - this.lastActivity
    }
  }

  /**
   * Advertise service (make board discoverable)
   * In WASM, this could expose the board instance globally
   * @private
   */
  _advertiseService() {
    // Make board instance globally accessible for web apps
    if (typeof window !== 'undefined') {
      window.circuitPythonBoard = this.board
      console.log('[Workflow] Board advertised at window.circuitPythonBoard')
    }

    // Could also emit events or use other discovery mechanisms
  }

  /**
   * Start activity monitoring
   * @private
   */
  _startActivityMonitoring() {
    // Monitor for activity periodically
    setInterval(() => {
      this._updateActiveState()
    }, 5000)  // Check every 5 seconds
  }

  /**
   * Update active state based on current conditions
   * @private
   */
  _updateActiveState() {
    const wasActive = this.active
    this.active = this.isActive()

    if (wasActive !== this.active) {
      console.log(`[Workflow] Active state changed: ${this.active}`)
      this._notifyActiveStateChange()
    }
  }

  /**
   * Mark recent activity
   * @private
   */
  _markActivity() {
    this.lastActivity = Date.now()
    if (!this.active) {
      this.active = true
      this._notifyActiveStateChange()
    }
  }

  /**
   * Generate unique connection ID
   * @private
   */
  _generateConnectionId() {
    return `conn_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
  }

  /**
   * Find connection by ID
   * @private
   */
  _findConnection(id) {
    for (const conn of this.connections) {
      if (conn.id === id) {
        return conn
      }
    }
    return null
  }

  /**
   * Notify of connection change
   * @private
   */
  _notifyConnectionChange() {
    // Could emit events here for web apps to listen to
    if (typeof window !== 'undefined' && window.dispatchEvent) {
      window.dispatchEvent(new CustomEvent('circuitpython-connection-change', {
        detail: {
          connections: this.getConnectionCount(),
          active: this.isActive()
        }
      }))
    }
  }

  /**
   * Notify of active state change
   * @private
   */
  _notifyActiveStateChange() {
    // Could emit events here for web apps to listen to
    if (typeof window !== 'undefined' && window.dispatchEvent) {
      window.dispatchEvent(new CustomEvent('circuitpython-active-change', {
        detail: {
          active: this.active
        }
      }))
    }
  }
}
