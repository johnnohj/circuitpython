/**
 * CircuitPythonBoard - Represents a complete virtual CircuitPython board
 *
 * This class provides a board-centric API for interacting with CircuitPython WASM,
 * treating it like a physical microcontroller rather than just a Python VM.
 *
 * Usage:
 *   const board = new CircuitPythonBoard({
 *     canvasId: 'display',
 *     storagePrefix: 'circuitpy'
 *   })
 *   await board.connect()
 *
 *   // Access hardware like a physical board
 *   const led = board.gpio.getPin(13)
 *   led.setDirection('output')
 *   led.setValue(true)
 *
 *   // Access REPL like serial connection
 *   board.serial.onData((data) => terminal.write(data))
 *   board.serial.write('print("hello")\n')
 */

import { SupervisorController } from '../controllers/supervisor.js'
import { SerialInterface } from '../controllers/serial.js'
import { GPIOController } from '../controllers/gpio.js'
import { I2CController } from '../controllers/i2c.js'
import { SPIController } from '../controllers/spi.js'
import { DisplayController } from '../controllers/display.js'
import { StorageController } from '../controllers/storage.js'
import { WorkflowController } from '../controllers/workflow.js'

export class CircuitPythonBoard {
  /**
   * Create a new CircuitPython board instance
   * @param {Object} config - Configuration options
   * @param {string} config.canvasId - Canvas element ID for display rendering
   * @param {string} config.storagePrefix - Prefix for IndexedDB storage
   * @param {number} config.heapSize - WASM heap size in bytes
   * @param {string} config.variant - Variant to load (standard, integrated, asyncified)
   */
  constructor(config = {}) {
    this.config = {
      canvasId: config.canvasId || null,
      storagePrefix: config.storagePrefix || 'circuitpy',
      heapSize: config.heapSize || 128 * 1024,
      variant: config.variant || 'standard',
      ...config
    }

    // Board components (initialized in connect())
    this.supervisor = null  // SupervisorController
    this.serial = null      // SerialInterface
    this.gpio = null        // GPIOController
    this.i2c = null         // I2CController
    this.spi = null         // SPIController
    this.display = null     // DisplayController
    this.storage = null     // StorageController
    this.workflow = null    // WorkflowController
    this.module = null      // WASM module

    // Connection state
    this._connected = false
    this._connectionPromise = null
  }

  /**
   * Connect to the board (initialize everything)
   * This is the main initialization method that sets up all controllers
   * and starts the CircuitPython runtime.
   *
   * @returns {Promise<CircuitPythonBoard>} This board instance
   */
  async connect() {
    // Prevent multiple simultaneous connections
    if (this._connectionPromise) {
      return this._connectionPromise
    }

    this._connectionPromise = this._doConnect()
    return this._connectionPromise
  }

  async _doConnect() {
    if (this._connected) {
      return this
    }

    // 1. Load WASM module
    console.log('[CircuitPythonBoard] Loading WASM module...')
    this.module = await this._loadWASMModule()

    // 2. Initialize storage first (needed by supervisor)
    console.log('[CircuitPythonBoard] Initializing storage...')
    this.storage = new StorageController(this.module, this)
    await this.storage.initialize()

    // 3. Initialize hardware controllers
    console.log('[CircuitPythonBoard] Initializing hardware controllers...')
    this.gpio = new GPIOController(this.module, this)
    this.i2c = new I2CController(this.module, this)
    this.spi = new SPIController(this.module, this)
    this.serial = new SerialInterface(this.module, this)

    // 4. Initialize display if canvas is provided
    if (this.config.canvasId) {
      console.log('[CircuitPythonBoard] Initializing display...')
      this.display = new DisplayController(this.module, this, this.config.canvasId)
      await this.display.initialize()
    }

    // 5. Register controllers as peripherals for C code access
    console.log('[CircuitPythonBoard] Registering peripherals...')
    if (this.module.peripherals) {
      this.module.peripherals.attach('gpio', this.gpio)
      this.module.peripherals.attach('i2c', this.i2c)
      this.module.peripherals.attach('spi', this.spi)
      this.module.peripherals.attach('serial', this.serial)
      if (this.display) {
        this.module.peripherals.attach('display', this.display)
      }
      if (this.storage) {
        this.module.peripherals.attach('storage', this.storage)
      }
      console.log('[CircuitPythonBoard] Peripherals registered:', this.module.peripherals.list())
    }

    // 6. Initialize supervisor (it starts the runtime)
    console.log('[CircuitPythonBoard] Initializing supervisor...')
    this.supervisor = new SupervisorController(this.module, this)
    await this.supervisor.initialize()

    // 7. Initialize workflow system (enables web app connection)
    console.log('[CircuitPythonBoard] Initializing workflow...')
    this.workflow = new WorkflowController(this)
    await this.workflow.start()

    // 8. Expose board instance for library functions to access
    // This allows library_*.js to delegate to controllers
    if (this.module && typeof this.module === 'object') {
      this.module._circuitPythonBoard = this
      console.log('[CircuitPythonBoard] Exposed to Module._circuitPythonBoard')
    }

    // 9. Board is ready
    this._connected = true
    console.log('[CircuitPythonBoard] Board connected and ready')
    console.log('[CircuitPythonBoard] Access via window.circuitPythonBoard')

    return this
  }

  /**
   * Load the WASM module based on configuration
   * @private
   */
  async _loadWASMModule() {
    // Import the core CircuitPython loader
    const { loadCircuitPython } = await import('./api.js')
    return loadCircuitPython({
      heapsize: this.config.heapSize,
      verbose: this.config.verbose || false,
      autoRun: false,  // Board handles workflow
      filesystem: this.config.filesystem || 'indexeddb'
    })
  }

  /**
   * Check if board is connected
   * @returns {boolean} True if connected
   */
  isConnected() {
    return this._connected
  }

  /**
   * Reset the board (soft reset)
   * This is equivalent to pressing Ctrl+D in the REPL or calling supervisor.reload()
   */
  async reset() {
    if (!this._connected) {
      throw new Error('Board not connected')
    }
    await this.supervisor.reset()
  }

  /**
   * Enter safe mode
   * In safe mode, user code doesn't run and only basic functionality is available
   */
  async safeMode() {
    if (!this._connected) {
      throw new Error('Board not connected')
    }
    await this.supervisor.enterSafeMode()
  }

  /**
   * Get a pin by number (convenience method)
   * Equivalent to board.GPIO13 in Python
   *
   * @param {number} number - Pin number
   * @returns {Pin} Pin object
   */
  getPin(number) {
    if (!this._connected) {
      throw new Error('Board not connected')
    }
    return this.gpio.getPin(number)
  }

  /**
   * Disconnect and cleanup
   * This should be called when done with the board to free resources
   */
  async disconnect() {
    if (!this._connected) {
      return
    }

    console.log('[CircuitPythonBoard] Disconnecting...')

    // Shutdown in reverse order of initialization
    if (this.workflow) {
      await this.workflow.stop()
    }

    if (this.supervisor) {
      await this.supervisor.shutdown()
    }

    // Detach peripherals
    if (this.module && this.module.peripherals) {
      const registered = this.module.peripherals.list()
      for (const name of registered) {
        this.module.peripherals.detach(name)
      }
      console.log('[CircuitPythonBoard] Detached all peripherals')
    }

    if (this.display) {
      this.display.cleanup()
    }

    if (this.storage) {
      await this.storage.cleanup()
    }

    // Clear references
    this.workflow = null
    this.supervisor = null
    this.serial = null
    this.gpio = null
    this.i2c = null
    this.spi = null
    this.display = null
    this.storage = null
    this.module = null

    this._connected = false
    this._connectionPromise = null

    console.log('[CircuitPythonBoard] Disconnected')
  }

  /**
   * Get board information (equivalent to reading board properties in Python)
   * @returns {Object} Board information
   */
  getBoardInfo() {
    return {
      connected: this._connected,
      variant: this.config.variant,
      platform: 'webassembly',
      implementation: 'CircuitPython',
      hasDisplay: this.display !== null,
      storagePrefix: this.config.storagePrefix,
      workflowActive: this.workflow?.isActive() || false,
      connections: this.workflow?.getConnectionCount() || 0
    }
  }

  /**
   * Get full board status (includes all subsystems)
   * @returns {Object} Complete board status
   */
  getStatus() {
    return {
      board: this.getBoardInfo(),
      supervisor: this.supervisor?.getRuntimeInfo() || null,
      workflow: this.workflow?.getStatus() || null,
      serial: {
        connected: this.serial?.isConnected() || false,
        replEnabled: this.serial?.replEnabled || false
      },
      storage: this.storage?.getMountInfo() || null,
      display: this.display?.getInfo() || null,
      hardware: {
        gpio: this.gpio?.getAllPins().size || 0,
        i2c: this.i2c?.buses.size || 0,
        spi: this.spi?.buses.size || 0
      }
    }
  }
}
