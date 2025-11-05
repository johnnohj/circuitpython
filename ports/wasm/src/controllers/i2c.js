/**
 * I2CBus - Represents a single I2C bus with automatic event notifications
 */
class I2CBus {
  constructor(index, controller) {
    this._index = index
    this._controller = controller
    this._frequency = 100000  // Default 100kHz
    this._locked = false
    this._devices = new Map()  // addr → device object
    this._transactionCallbacks = []
    this._probeCallbacks = []
    this._lastTransaction = null
    this._lastProbe = null
  }

  get index() {
    return this._index
  }

  get frequency() {
    return this._frequency
  }

  set frequency(val) {
    this._frequency = val
  }

  get locked() {
    return this._locked
  }

  set locked(val) {
    this._locked = Boolean(val)
  }

  // Property setter - called by C via store_attr()
  // This fires transaction callbacks AUTOMATICALLY!
  set lastTransaction(transaction) {
    this._lastTransaction = transaction
    this._notifyTransaction(transaction)
  }

  get lastTransaction() {
    return this._lastTransaction
  }

  // Property setter for probe events
  set lastProbe(probe) {
    this._lastProbe = probe
    this._notifyProbe(probe)
  }

  get lastProbe() {
    return this._lastProbe
  }

  /**
   * Register a callback for transaction events
   * @param {Function} callback - Called with transaction object
   * @returns {Function} Unregister function
   */
  onTransaction(callback) {
    this._transactionCallbacks.push(callback)
    return () => {
      const idx = this._transactionCallbacks.indexOf(callback)
      if (idx >= 0) this._transactionCallbacks.splice(idx, 1)
    }
  }

  /**
   * Register a callback for probe events
   * @param {Function} callback - Called with probe result
   * @returns {Function} Unregister function
   */
  onProbe(callback) {
    this._probeCallbacks.push(callback)
    return () => {
      const idx = this._probeCallbacks.indexOf(callback)
      if (idx >= 0) this._probeCallbacks.splice(idx, 1)
    }
  }

  _notifyTransaction(transaction) {
    for (const cb of this._transactionCallbacks) {
      cb(transaction)
    }
  }

  _notifyProbe(probe) {
    for (const cb of this._probeCallbacks) {
      cb(probe)
    }
  }

  /**
   * Register a virtual I2C device on this bus
   * @param {number} address - Device address (7-bit)
   * @param {Object} device - Device emulator with onRead/onWrite methods
   */
  registerDevice(address, device) {
    if (this._devices.has(address)) {
      console.warn(`[I2C Bus ${this._index}] Device at 0x${address.toString(16)} already registered, replacing`)
    }
    this._devices.set(address, device)
    console.log(`[I2C Bus ${this._index}] Registered virtual device at 0x${address.toString(16)}`)
  }

  /**
   * Unregister a virtual device
   * @param {number} address - Device address (7-bit)
   */
  unregisterDevice(address) {
    if (this._devices.has(address)) {
      this._devices.delete(address)
      console.log(`[I2C Bus ${this._index}] Unregistered device at 0x${address.toString(16)}`)
    }
  }

  /**
   * Create a simple register-based device
   * @param {number} address - Device address
   * @param {Object} registers - Initial register values
   * @returns {Object} Device emulator
   */
  createSimpleDevice(address, registers = {}) {
    let currentRegister = 0

    const device = {
      registers: new Map(Object.entries(registers)),

      onWrite(data, stop) {
        if (data.length === 0) return

        // First byte is register address
        currentRegister = data[0]

        // Subsequent bytes are data to write
        for (let i = 1; i < data.length; i++) {
          this.registers.set(currentRegister, data[i])
          currentRegister++
        }
      },

      onRead(length) {
        const result = new Uint8Array(length)

        for (let i = 0; i < length; i++) {
          const value = this.registers.get(currentRegister) || 0
          result[i] = value
          currentRegister++
        }

        return result
      }
    }

    this.registerDevice(address, device)
    return device
  }

  /**
   * Get all registered device addresses on this bus
   * @returns {number[]} Array of device addresses
   */
  getDeviceAddresses() {
    return Array.from(this._devices.keys())
  }
}

/**
 * I2CController - Manages I2C bus communication
 *
 * This bridges Python's busio.I2C with virtual I2C hardware.
 * Uses JsProxy for automatic event notifications.
 *
 * Handles:
 * - I2C bus creation and configuration
 * - Device scanning and addressing
 * - Read/write operations
 * - Virtual device emulation
 * - Automatic transaction events
 */
export class I2CController {
  /**
   * Create a new I2C controller
   * @param {Object} wasmModule - The loaded WASM module
   * @param {CircuitPythonBoard} board - The parent board instance
   */
  constructor(wasmModule, board) {
    this.module = wasmModule
    this.board = board

    // I2C bus instances (index → I2CBus object)
    this.buses = new Map()
  }

  /**
   * Get or create an I2C bus by index
   * @param {number} index - Bus index (0-7)
   * @returns {I2CBus} Bus object
   */
  getBus(index) {
    if (!this.buses.has(index)) {
      const bus = new I2CBus(index, this)
      this.buses.set(index, bus)
      console.log(`[I2C] Created bus ${index}`)
    }
    return this.buses.get(index)
  }

  /**
   * Reset all I2C buses (called during soft reset)
   */
  resetAll() {
    console.log('[I2C] Resetting all buses...')

    // Unlock all buses
    for (const bus of this.buses.values()) {
      bus.locked = false
    }

    // Clear all buses (they will be recreated when needed)
    this.buses.clear()

    console.log('[I2C] Reset complete')
  }
}
