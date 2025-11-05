/**
 * SPIBus - Represents a single SPI bus with automatic event notifications
 */
class SPIBus {
  constructor(index, controller) {
    this._index = index
    this._controller = controller
    this._baudrate = 250000  // Default 250kHz
    this._polarity = 0
    this._phase = 0
    this._bits = 8
    this._locked = false
    this._devices = new Map()  // cs_pin → device object
    this._transactionCallbacks = []
    this._lastTransaction = null
  }

  get index() {
    return this._index
  }

  get baudrate() {
    return this._baudrate
  }

  set baudrate(val) {
    this._baudrate = val
  }

  get polarity() {
    return this._polarity
  }

  set polarity(val) {
    this._polarity = val
  }

  get phase() {
    return this._phase
  }

  set phase(val) {
    this._phase = val
  }

  get bits() {
    return this._bits
  }

  set bits(val) {
    this._bits = val
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

  _notifyTransaction(transaction) {
    for (const cb of this._transactionCallbacks) {
      cb(transaction)
    }
  }

  /**
   * Register a virtual SPI device on this bus
   * @param {number} csPin - Chip select pin number
   * @param {Object} device - Device emulator with onWrite/onRead/onWriteRead methods
   */
  registerDevice(csPin, device) {
    if (this._devices.has(csPin)) {
      console.warn(`[SPI Bus ${this._index}] Device with CS=${csPin} already registered, replacing`)
    }
    this._devices.set(csPin, device)
    console.log(`[SPI Bus ${this._index}] Registered virtual device with CS=${csPin}`)
  }

  /**
   * Unregister a virtual device
   * @param {number} csPin - Chip select pin number
   */
  unregisterDevice(csPin) {
    if (this._devices.has(csPin)) {
      this._devices.delete(csPin)
      console.log(`[SPI Bus ${this._index}] Unregistered device with CS=${csPin}`)
    }
  }

  /**
   * Create a simple buffered device
   * @param {number} csPin - Chip select pin
   * @param {number} bufferSize - Buffer size
   * @returns {Object} Device emulator
   */
  createBufferedDevice(csPin, bufferSize = 1024) {
    const device = {
      buffer: new Uint8Array(bufferSize),
      writePtr: 0,
      readPtr: 0,

      onWrite(data) {
        for (const byte of data) {
          this.buffer[this.writePtr % bufferSize] = byte
          this.writePtr++
        }
      },

      onRead(length) {
        const result = new Uint8Array(length)

        for (let i = 0; i < length; i++) {
          result[i] = this.buffer[this.readPtr % bufferSize]
          this.readPtr++
        }

        return result
      },

      onWriteRead(writeData, readLength) {
        this.onWrite(writeData)
        return this.onRead(readLength)
      }
    }

    this.registerDevice(csPin, device)
    return device
  }

  /**
   * Get all registered device CS pins on this bus
   * @returns {number[]} Array of CS pin numbers
   */
  getDeviceCSPins() {
    return Array.from(this._devices.keys())
  }
}

/**
 * SPIController - Manages SPI bus communication
 *
 * This bridges Python's busio.SPI with virtual SPI hardware.
 * Uses JsProxy for automatic event notifications.
 *
 * Handles:
 * - SPI bus creation and configuration
 * - Clock, MOSI, MISO management
 * - Read/write operations
 * - Virtual device emulation
 * - Automatic transaction events
 */
export class SPIController {
  /**
   * Create a new SPI controller
   * @param {Object} wasmModule - The loaded WASM module
   * @param {CircuitPythonBoard} board - The parent board instance
   */
  constructor(wasmModule, board) {
    this.module = wasmModule
    this.board = board

    // SPI bus instances (index → SPIBus object)
    this.buses = new Map()
  }

  /**
   * Get or create an SPI bus by index
   * @param {number} index - Bus index (0-3)
   * @returns {SPIBus} Bus object
   */
  getBus(index) {
    if (!this.buses.has(index)) {
      const bus = new SPIBus(index, this)
      this.buses.set(index, bus)
      console.log(`[SPI] Created bus ${index}`)
    }
    return this.buses.get(index)
  }

  /**
   * Reset all SPI buses (called during soft reset)
   */
  resetAll() {
    console.log('[SPI] Resetting all buses...')

    // Unlock all buses
    for (const bus of this.buses.values()) {
      bus.locked = false
    }

    // Clear all buses (they will be recreated when needed)
    this.buses.clear()

    console.log('[SPI] Reset complete')
  }
}
