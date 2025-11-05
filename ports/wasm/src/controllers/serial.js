/**
 * SerialInterface - Manages serial/UART communication and REPL
 *
 * This class provides the serial interface for CircuitPython WASM,
 * similar to connecting to a physical board via USB serial.
 *
 * It handles:
 * - REPL input/output
 * - UART communication
 * - Console output (print statements)
 * - Ctrl+C (KeyboardInterrupt) and Ctrl+D (soft reset)
 */

export class SerialInterface {
  /**
   * Create a new serial interface
   * @param {Object} wasmModule - The loaded WASM module
   * @param {CircuitPythonBoard} board - The parent board instance
   */
  constructor(wasmModule, board) {
    this.module = wasmModule
    this.board = board

    // Serial state
    this.connected = false
    this.replEnabled = false

    // Input/output buffers
    this.inputBuffer = []
    this.outputCallbacks = []

    // UART state
    this.uartInstances = new Map()  // UART port number -> config
  }

  /**
   * Check if serial connection is active
   * @returns {boolean} True if connected
   */
  isConnected() {
    return this.connected
  }

  /**
   * Connect serial interface
   * This should be called automatically during board initialization
   */
  connect() {
    if (this.connected) {
      return
    }

    console.log('[Serial] Connecting...')
    this.connected = true

    // Set up output handling from WASM
    this._setupOutputHandling()

    console.log('[Serial] Connected')
  }

  /**
   * Disconnect serial interface
   */
  disconnect() {
    if (!this.connected) {
      return
    }

    console.log('[Serial] Disconnecting...')
    this.connected = false
    this.replEnabled = false
    this.inputBuffer = []
    this.outputCallbacks = []

    console.log('[Serial] Disconnected')
  }

  /**
   * Enable REPL mode
   * This should be called by the supervisor when entering REPL
   */
  enableREPL() {
    if (this.replEnabled) {
      return
    }

    console.log('[Serial] Enabling REPL...')
    this.replEnabled = true

    // Connect to existing REPL init if available
    if (!this.connected) {
      this.connect()
    }
  }

  /**
   * Disable REPL mode
   */
  disableREPL() {
    if (!this.replEnabled) {
      return
    }

    console.log('[Serial] Disabling REPL...')
    this.replEnabled = false
  }

  /**
   * Write data to serial input (from user to CircuitPython)
   * This is like typing into a serial terminal
   *
   * @param {string|Uint8Array} data - Data to write
   */
  write(data) {
    if (!this.connected) {
      console.warn('[Serial] Cannot write - not connected')
      return
    }

    // Convert string to bytes if needed
    const bytes = typeof data === 'string'
      ? new TextEncoder().encode(data)
      : data

    // Add to input buffer
    for (const byte of bytes) {
      this.inputBuffer.push(byte)
    }

    // Process input if in REPL mode
    if (this.replEnabled) {
      this._processREPLInput(bytes)
    }
  }

  /**
   * Process a single character for REPL
   * This handles special keys like Ctrl+C, Ctrl+D, backspace, etc.
   *
   * @param {number} charCode - Character code to process
   */
  processChar(charCode) {
    if (!this.connected || !this.replEnabled) {
      console.warn('[Serial] Cannot process char - REPL not active')
      return
    }

    // Handle special control characters
    if (charCode === 0x03) {  // Ctrl+C - KeyboardInterrupt
      console.log('[Serial] Ctrl+C - sending KeyboardInterrupt')
      this._sendKeyboardInterrupt()
      return
    }

    if (charCode === 0x04) {  // Ctrl+D - soft reset
      console.log('[Serial] Ctrl+D - requesting soft reset')
      this._requestSoftReset()
      return
    }

    // Pass character to REPL processor
    try {
      if (this.module.ccall) {
        this.module.ccall('mp_js_repl_process_char', 'null', ['number'], [charCode])
      }
    } catch (e) {
      console.warn('[Serial] Error processing character:', e.message)
    }
  }

  /**
   * Register callback for receiving output data (from CircuitPython to user)
   * This is like reading from a serial terminal
   *
   * @param {Function} callback - Callback function(data: string)
   * @returns {Function} Unregister function
   */
  onData(callback) {
    if (typeof callback !== 'function') {
      throw new Error('Callback must be a function')
    }

    this.outputCallbacks.push(callback)

    // Return unregister function
    return () => {
      const idx = this.outputCallbacks.indexOf(callback)
      if (idx >= 0) {
        this.outputCallbacks.splice(idx, 1)
      }
    }
  }

  /**
   * Get number of bytes available in input buffer
   * This mirrors supervisor.runtime.serial_bytes_available
   *
   * @returns {number} Number of bytes available
   */
  getInputBufferSize() {
    return this.inputBuffer.length
  }

  /**
   * Read bytes from input buffer
   * This is used by UART/serial implementations to get data
   *
   * @param {number} count - Number of bytes to read
   * @returns {Uint8Array} Read bytes
   */
  readBytes(count) {
    const available = Math.min(count, this.inputBuffer.length)
    const bytes = new Uint8Array(available)

    for (let i = 0; i < available; i++) {
      bytes[i] = this.inputBuffer.shift()
    }

    return bytes
  }

  /**
   * Emit output data to all registered callbacks
   * This is called when CircuitPython produces output
   *
   * @param {string} data - Output data
   * @private
   */
  _emitOutput(data) {
    for (const callback of this.outputCallbacks) {
      try {
        callback(data)
      } catch (e) {
        console.error('[Serial] Error in output callback:', e)
      }
    }
  }

  /**
   * Set up output handling from WASM module
   * This intercepts stdout/stderr from CircuitPython
   * @private
   */
  _setupOutputHandling() {
    // Hook into WASM's stdout/stderr
    // This will be connected to the existing output handling in api.js

    // For now, we'll rely on the existing mp_js_stdout mechanism
    // which should call our output handlers
  }

  /**
   * Process REPL input bytes
   * @param {Uint8Array} bytes - Input bytes
   * @private
   */
  _processREPLInput(bytes) {
    for (const byte of bytes) {
      this.processChar(byte)
    }
  }

  /**
   * Send KeyboardInterrupt to Python (Ctrl+C)
   * @private
   */
  _sendKeyboardInterrupt() {
    try {
      if (this.module.ccall) {
        this.module.ccall('mp_keyboard_interrupt', 'null', [], [])
      }
    } catch (e) {
      console.warn('[Serial] Error sending KeyboardInterrupt:', e.message)
    }
  }

  /**
   * Request soft reset (Ctrl+D)
   * @private
   */
  _requestSoftReset() {
    // Write newline before reset for clean output
    this._emitOutput('\r\n')

    // Request reset from supervisor
    if (this.board.supervisor) {
      this.board.supervisor.reset().catch(e => {
        console.error('[Serial] Error during soft reset:', e)
      })
    }
  }

  /**
   * Create a UART instance
   * This is called by busio.UART in Python
   *
   * @param {number} port - UART port number
   * @param {Object} config - UART configuration
   * @returns {Object} UART instance
   */
  createUART(port, config = {}) {
    const uart = {
      port,
      baudrate: config.baudrate || 9600,
      bits: config.bits || 8,
      parity: config.parity || null,
      stop: config.stop || 1,
      tx: config.tx || null,
      rx: config.rx || null,
      rxBuffer: [],
      txBuffer: []
    }

    this.uartInstances.set(port, uart)
    console.log(`[Serial] Created UART${port} at ${uart.baudrate} baud`)

    return uart
  }

  /**
   * Destroy a UART instance
   * @param {number} port - UART port number
   */
  destroyUART(port) {
    if (this.uartInstances.has(port)) {
      this.uartInstances.delete(port)
      console.log(`[Serial] Destroyed UART${port}`)
    }
  }

  /**
   * Write to a UART port
   * @param {number} port - UART port number
   * @param {Uint8Array} data - Data to write
   */
  uartWrite(port, data) {
    const uart = this.uartInstances.get(port)
    if (!uart) {
      console.warn(`[Serial] UART${port} not found`)
      return
    }

    // For now, just emit as serial output
    // In a full implementation, this would go to specific UART pins
    const text = new TextDecoder().decode(data)
    this._emitOutput(text)
  }

  /**
   * Read from a UART port
   * @param {number} port - UART port number
   * @param {number} count - Number of bytes to read
   * @returns {Uint8Array} Read bytes
   */
  uartRead(port, count) {
    const uart = this.uartInstances.get(port)
    if (!uart) {
      console.warn(`[Serial] UART${port} not found`)
      return new Uint8Array(0)
    }

    // Read from UART's RX buffer
    const available = Math.min(count, uart.rxBuffer.length)
    const bytes = new Uint8Array(available)

    for (let i = 0; i < available; i++) {
      bytes[i] = uart.rxBuffer.shift()
    }

    return bytes
  }

  /**
   * Check bytes available on UART port
   * @param {number} port - UART port number
   * @returns {number} Number of bytes available
   */
  uartBytesAvailable(port) {
    const uart = this.uartInstances.get(port)
    return uart ? uart.rxBuffer.length : 0
  }
}
